"""allow_wildcard_certs

Revision ID: 58e440314c2a
Revises: 0bee61aa9425
Create Date: 2022-05-12 12:15:55.343743

"""

# revision identifiers, used by Alembic.
revision = '58e440314c2a'
down_revision = 'a06d8f8462d9'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.add_column('ps_transports', sa.Column('allow_wildcard_certs', type_=yesno_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_transports_allow_wildcard_certs_yesno_values', 'ps_transports')
    op.drop_column('ps_transports', 'allow_wildcard_certs')
