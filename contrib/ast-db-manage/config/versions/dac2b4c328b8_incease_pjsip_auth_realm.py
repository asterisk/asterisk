"""incease pjsip auth realm

Revision ID: dac2b4c328b8
Revises: f5b0e7427449
Create Date: 2023-09-23 02:15:24.270526

"""

# revision identifiers, used by Alembic.
revision = 'dac2b4c328b8'
down_revision = 'f5b0e7427449'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_auths', 'realm', type_=sa.String(255))


def downgrade():
    op.alter_column('ps_auths', 'realm', type_=sa.String(40))
