"""update pjsip tls method list

Revision ID: 37a5332640e2
Revises: dac2b4c328b8
Create Date: 2023-11-14 18:02:18.857452

"""

# revision identifiers, used by Alembic.
revision = '37a5332640e2'
down_revision = 'dac2b4c328b8'

from alembic import op
from sqlalchemy.dialects.postgresql import ENUM
import sqlalchemy as sa

PJSIP_TRANSPORT_METHOD_OLD_NAME = 'pjsip_transport_method_values'
PJSIP_TRANSPORT_METHOD_NEW_NAME = 'pjsip_transport_method_values_v2'

PJSIP_TRANSPORT_METHOD_OLD_VALUES = ['default', 'unspecified', 'tlsv1', 'sslv2',
                                     'sslv3', 'sslv23']
PJSIP_TRANSPORT_METHOD_NEW_VALUES = ['default', 'unspecified',
                                     'tlsv1', 'tlsv1_1', 'tlsv1_2', 'tlsv1_3',
                                     'sslv2', 'sslv23', 'sslv3']

PJSIP_TRANSPORT_METHOD_OLD_TYPE = sa.Enum(*PJSIP_TRANSPORT_METHOD_OLD_VALUES,
                                          name=PJSIP_TRANSPORT_METHOD_OLD_NAME)
PJSIP_TRANSPORT_METHOD_NEW_TYPE = sa.Enum(*PJSIP_TRANSPORT_METHOD_NEW_VALUES,
                                          name=PJSIP_TRANSPORT_METHOD_NEW_NAME)
def upgrade():
    if op.get_context().bind.dialect.name == 'postgresql':
        enum = PJSIP_TRANSPORT_METHOD_NEW_TYPE
        enum.create(op.get_bind(), checkfirst=False)

    op.alter_column('ps_transports', 'method',
                    type_=PJSIP_TRANSPORT_METHOD_NEW_TYPE,
                    existing_type=PJSIP_TRANSPORT_METHOD_OLD_TYPE,
                    postgresql_using='method::text::' + PJSIP_TRANSPORT_METHOD_NEW_NAME)

    if op.get_context().bind.dialect.name == 'postgresql':
        ENUM(name=PJSIP_TRANSPORT_METHOD_OLD_NAME).drop(op.get_bind(), checkfirst=False)

def downgrade():
    # First we need to ensure that columns are not using the enum values
    # that are going away.
    op.execute("UPDATE ps_transports SET method = 'tlsv1' WHERE method IN ('tlsv1_1', 'tlsv1_2', 'tlsv1_3')")

    if op.get_context().bind.dialect.name == 'postgresql':
        enum = PJSIP_TRANSPORT_METHOD_OLD_TYPE
        enum.create(op.get_bind(), checkfirst=False)

    op.alter_column('ps_transports', 'method',
                    type_=PJSIP_TRANSPORT_METHOD_OLD_TYPE,
                    existing_type=PJSIP_TRANSPORT_METHOD_NEW_TYPE,
                    postgresql_using='method::text::' + PJSIP_TRANSPORT_METHOD_OLD_NAME)

    if op.get_context().bind.dialect.name == 'postgresql':
        ENUM(name=PJSIP_TRANSPORT_METHOD_NEW_NAME).drop(op.get_bind(), checkfirst=False)
