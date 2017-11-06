"""add_dtls_auto_generate_cert

Revision ID: 041c0d3d1857
Revises: de83fac997e2
Create Date: 2017-10-30 14:28:10.548395

"""

# revision identifiers, used by Alembic.
revision = '041c0d3d1857'
down_revision = 'de83fac997e2'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    ############################# Enums ##############################

    # yesno_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.add_column('ps_endpoints', sa.Column('dtls_auto_generate_cert', yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_dtls_auto_generate_cert_yesno_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'dtls_auto_generate_cert')
